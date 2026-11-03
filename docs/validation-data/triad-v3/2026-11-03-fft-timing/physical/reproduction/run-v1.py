#!/usr/bin/env python3
"""Execute an explicitly selected frozen FFT campaign with bounded children."""
from __future__ import annotations
import argparse
from concurrent.futures import FIRST_COMPLETED,ThreadPoolExecutor,wait
import hashlib
import json
import math
import os
from pathlib import Path
import subprocess
import sys
import time


def sha(path):
    h=hashlib.sha256()
    with Path(path).open('rb') as stream:
        for block in iter(lambda:stream.read(1024*1024),b''):h.update(block)
    return h.hexdigest()


def write(path,value):
    path.parent.mkdir(parents=True,exist_ok=True)
    with path.open('x') as stream:json.dump(value,stream,indent=2,sort_keys=True,allow_nan=False);stream.write('\n')


def translated(argv,bindings):
    out=[]
    for item in argv:
        for key,value in bindings.items():item=item.replace('{'+key+'}',str(value))
        if '{' in item or '}' in item:raise ValueError('unresolved argument token')
        out.append(item)
    return out


def stable(plan,args,runner_hash,bounded_hash):
    if sha(args.binary)!=plan['native_binary_sha256']:raise ValueError('native executable differs')
    if sha(__file__)!=runner_hash or sha(args.root/'tools/triad_bounded_run.py')!=bounded_hash:raise ValueError('runner changed')
    for name,digest in plan['source_file_sha256'].items():
        if sha(args.root/name)!=digest:raise ValueError('dependency source changed: '+name)


def bounded(argv,args,ident,stage,timeout):
    log=args.out/'logs'/(ident+'-'+stage+'.log');status=args.out/'logs'/(ident+'-'+stage+'.status.json')
    command=[str(args.python),str(args.root/'tools/triad_bounded_run.py'),'--cwd',str(args.root),
        '--log',str(log),'--status',str(status),'--timeout',str(timeout),'--max-seconds',str(timeout),
        '--term-grace','3','--',*argv]
    env=os.environ.copy();env.update(OMP_NUM_THREADS='1',OPENBLAS_NUM_THREADS='1',MKL_NUM_THREADS='1',NUMEXPR_NUM_THREADS='1')
    started=time.monotonic()
    # The bounded helper owns/reaps the child group. Do not interrupt that
    # cleanup with a competing outer timeout/kill path.
    proc=subprocess.run(command,cwd=args.root,env=env,capture_output=True,text=True,check=False)
    value=json.loads(status.read_text()) if status.is_file() else {}
    okay=(proc.returncode==0 and value.get('outcome')=='completed' and value.get('exit_code')==0
        and value.get('child_exit_code')==0 and value.get('timed_out') is False
        and value.get('cancelled') is False and value.get('termination',{}).get('group_empty') is True
        and value.get('termination',{}).get('direct_child_reaped',True) is True)
    return dict(argv=argv,argv_sha256=hashlib.sha256(json.dumps(argv,separators=(',',':')).encode()).hexdigest(),
        elapsed_seconds=time.monotonic()-started,status='completed' if okay else 'execution_failed',
        wrapper_returncode=proc.returncode,wrapper_output=proc.stdout[-2000:]+proc.stderr[-2000:],
        bounded_status=value,log=str(log),log_sha256=sha(log) if log.exists() else None,
        status_path=str(status),status_sha256=sha(status) if status.exists() else None)


def inventory(directory):
    return [dict(name=name,bytes=(directory/name).stat().st_size,sha256=sha(directory/name))
            for name in ('manifest.json','basis.c128','observation.c128','validity.u8')]


def actual_guard(workspace,row,score_argv):
    import numpy as np
    from scipy.fft import next_fast_len
    from triad_fft_acquire import SampledWorkspace
    from triad_group_acquire import frequency_offset_grid
    sampled=SampledWorkspace(workspace);n=sampled.samples;fs=sampled.sample_rate
    b=row['preflight'];flag=lambda name:score_argv[score_argv.index(name)+1]
    if n>b['conservative_sample_upper_bound'] or n>2_000_000:raise ValueError('actual raw extent exceeds frozen bound')
    if fs!=b['sample_rate_hz']:raise ValueError('actual sample rate differs')
    lag=int(np.ceil(np.longdouble(float(flag('--lag-max-seconds')))*np.longdouble(fs)))
    index=score_argv.index('--frequency-offset-grid');grid=tuple(map(float,score_argv[index+1:index+4]))
    frequencies=frequency_offset_grid(*grid);groups=(lag+1)*len(frequencies)
    if len(frequencies)>4096 or groups>1_000_000:raise ValueError('actual public search exceeds declared cap')
    if grid[2]>1/(4*n/fs):raise ValueError('frequency spacing exceeds full-calendar bound')
    nfft=next_fast_len(2*n-1)
    memory=sampled.input_bytes+n*16*20+nfft*16*20+(lag+1)*16*20+65536+min(512,lag+1)*128*1024
    if memory>=1024*1024**2:raise ValueError('actual CPU arrays/tile exceed1GiB')
    basis=sampled.array('basis.c128');supports=[]
    for candidate in sampled.selected:
        for column in range(2):
            support=np.flatnonzero(basis[candidate,:,column]!=0)
            if not len(support) or int(support[-1])+lag>=n:raise ValueError('public template support leaves actual capture')
            supports.append([int(support[0]),int(support[-1])])
    sampled.verify_unchanged()
    return dict(sample_count=n,sample_rate=fs,timing_count=lag+1,frequency_count=len(frequencies),
        effective_groups=groups,nfft=nfft,cpu_explicit_array_bytes=memory,public_template_support=supports,
        actual_vs_predicted_count_match=(n==b['predicted_sample_count']))


def outcome(values,threshold,bit,control):
    if len(values)!=2 or not all(math.isfinite(v) for v in values):raise ValueError('nonfinite bit likelihood')
    winner=0 if values[0]>=values[1] else 1;accepted=values[winner]>=threshold
    if control=='signal':return dict(correct_admitted=int(accepted and winner==bit),wrong_admitted=int(accepted and winner!=bit),missed=int(not accepted))
    return dict(false_admitted=int(accepted),not_admitted=int(not accepted))


def execute(row,trial,args,deadline):
    ident=trial['id'];record=dict(id=ident,case=row['case']['name'],bit=trial['bit'],control=trial['control'],status='not_started')
    bindings=dict(ROOT=args.root,BINARY=args.binary,PYTHON=args.python,OUTPUT=args.out)
    export=translated(trial['export_argv'],bindings);score=translated(trial['score_argv'],bindings)
    workspace=Path(export[export.index('--output')+1]);report=Path(score[score.index('--output')+1])
    if workspace.exists() or report.exists():raise ValueError('refusing existing capture/score output')
    if time.monotonic()+300+10>deadline:record['status']='not_started_deadline';return record
    record['export']=bounded(export,args,ident,'export',300)
    if record['export']['status']!='completed':record['status']='export_failed';return record
    try:
        record['input_files']=inventory(workspace)
        record['actual_guard']=actual_guard(workspace,row,score)
    except (OSError,ValueError,KeyError) as exc:record.update(status='actual_export_guard_failed',reason=str(exc));return record
    if time.monotonic()+900+10>deadline:record['status']='score_not_started_deadline';return record
    record['score']=bounded(score,args,ident,'score',900)
    after=inventory(workspace)
    record['input_files_unchanged']=after==record['input_files']
    if not record['input_files_unchanged']:record['status']='physical_input_changed';return record
    if record['score']['status']!='completed':record['status']='score_failed';return record
    try:
        data=json.loads(report.read_text());record['score_report_sha256']=sha(report)
        if data['input_sha256']!={item['name']:item['sha256'] for item in record['input_files']}:
            raise ValueError('scorer input hashes differ from bound physical capture')
        if data['receiver_source_sha256']!=args.source_file_sha256['tools/triad_fft_acquire.py']:
            raise ValueError('scorer source hash differs')
        expected_dependencies={name:args.source_file_sha256['tools/'+name] for name in (
            'triad_group_acquire.py','triad_gaussian_window.py','triad_noise_likelihood.py',
            'triad_gaussian_state.py','triad_state_workspace.py')}
        if data['dependency_sha256']!=expected_dependencies:raise ValueError('scorer dependency hashes differ')
        # FFT CLI emits one direct capture report, not the grouped driver's list.
        if data['geometry']['group_count']!=record['actual_guard']['effective_groups']:raise ValueError('scored groups differ from actual guard')
        values=data['bit_log_likelihoods'];evaluated=data['simulation_evaluation_only']
        if evaluated['signal_bit']!=trial['bit'] or evaluated['control']!=trial['control']:raise ValueError('post-decision evaluation identity differs')
        counts={gate:outcome(values,data[gate]['log_threshold'],trial['bit'],trial['control']) for gate in ('strict','relaxed')}
        if counts!=evaluated['outcomes']:raise ValueError('independent outcome recount differs')
        record['outcomes']=counts;record['bit_log_likelihoods']=values
        record['gates']={g:data[g]['log_threshold'] for g in ('strict','relaxed')}
        record['status']='completed'
    except (OSError,ValueError,KeyError) as exc:record.update(status='invalid_score_output',reason=str(exc))
    return record


def main():
    p=argparse.ArgumentParser(description=__doc__)
    for name in ('root','binary','python','plan','out'):p.add_argument('--'+name,type=Path,required=True)
    p.add_argument('--workers',type=int,default=4);p.add_argument('--seconds',type=float,default=2700)
    p.add_argument('--only-id',action='append',default=[])
    args=p.parse_args()
    if not 1<=args.workers<=8 or not 0<args.seconds<=2700:p.error('workers1..8 and campaign≤2700seconds required')
    for name in ('root','binary','plan','out'):setattr(args,name,getattr(args,name).resolve())
    args.python=args.python.absolute()
    if args.out.exists():p.error('output root must be fresh; no implicit reuse/overwrites')
    plan=json.loads(args.plan.read_text())
    if plan.get('schema')!='triad-fft-physical-campaign-v1' or not plan.get('source_unchanged') or not plan.get('binary_unchanged'):p.error('stable frozen campaign required')
    runner_hash=sha(__file__);bounded_hash=sha(args.root/'tools/triad_bounded_run.py')
    args.source_file_sha256=plan['source_file_sha256']
    stable(plan,args,runner_hash,bounded_hash)
    sys.path.insert(0,str(args.root/'tools'))
    all_jobs=[(row,trial) for row in plan['rows'] for trial in row['trials']]
    jobs=all_jobs
    if args.only_id:
        if not set(args.only_id)<={t['id'] for _,t in jobs}:p.error('unknown selected capture')
        jobs=[(r,t) for r,t in jobs if t['id'] in args.only_id]
    if not jobs:p.error('empty selection')
    for name in ('logs','workspaces','scores','records'):(args.out/name).mkdir(parents=True,exist_ok=True)
    started=time.monotonic();deadline=started+args.seconds;records=[];pending={};cursor=0
    with ThreadPoolExecutor(max_workers=args.workers) as pool:
        while cursor<len(jobs) or pending:
            while cursor<len(jobs) and len(pending)<args.workers:
                row,trial=jobs[cursor];cursor+=1
                pending[pool.submit(execute,row,trial,args,deadline)]=trial
            if pending:
                done,_=wait(pending,timeout=1,return_when=FIRST_COMPLETED)
                for future in done:
                    trial=pending.pop(future)
                    try:record=future.result()
                    except Exception as exc:record=dict(id=trial['id'],status='runner_error',reason=repr(exc))
                    records.append(record);write(args.out/'records'/(trial['id']+'.json'),record)
                    print(json.dumps(dict(finished=trial['id'],status=record['status'])),flush=True)
    unchanged=True;error=None
    try:stable(plan,args,runner_hash,bounded_hash)
    except (OSError,ValueError) as exc:unchanged=False;error=str(exc)
    payload=dict(schema='triad-fft-campaign-results-v1',plan_sha256=sha(args.plan),native_binary_sha256=plan['native_binary_sha256'],
        source_file_sha256=plan['source_file_sha256'],runner_sha256=runner_hash,bounded_runner_sha256=bounded_hash,
        source_and_binary_unchanged=unchanged,identity_error=error,planned_captures=len(all_jobs),selected_captures=len(jobs),
        all_selected_captures_accounted=len(records)==len(jobs),workers=args.workers,global_seconds=args.seconds,
        elapsed_seconds=time.monotonic()-started,preflight_row_statuses=[dict(case=r['case']['name'],status=r['status']) for r in plan['rows']],
        counts={s:sum(r['status']==s for r in records) for s in sorted({r['status'] for r in records})},records=records)
    write(args.out/'results.json',payload);print(json.dumps(dict(receipt=str(args.out/'results.json'),counts=payload['counts'])))
    return int(not(unchanged and payload['all_selected_captures_accounted']))


if __name__=='__main__':raise SystemExit(main())
