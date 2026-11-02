#!/usr/bin/env python3
"""Replay exactly three retained actual observations against frozen score plans."""
import pathlib,json,hashlib,sys,subprocess,os,time
import numpy as np
repo=pathlib.Path.cwd()
base=repo/'local-evidence/triad-spread-final/data'
out=repo/'local-evidence/triad-spread-replay'
sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest()
planpath=base/'reproduction/primary-score-plan.json'
plan=json.loads(planpath.read_text());jobs={j['name']:j for j in plan['jobs']}
cases=[('g001-m26','holdout/g001-m26/path-00216'),('g001-m25-refine','holdout/g001-m25-refine/path-10000'),('stable-005-m60-complete','holdout/stable-005-m60/path-00000')]
receipt={'schema':'triad-spread-retained-fp64-replay-v1','state':'running','runtime':sys.executable,'reproducer_sha256':sha(pathlib.Path(__file__)),'original_plan':str(planpath),'original_plan_sha256':sha(planpath),'arithmetic':'original CPU FP64, original one thread; unchanged receiver and scientific options','scope':'Three fixed actual observations; diagnostic replay, no additional independent trials or new confidence qualification.','rtol':1e-10,'atol':1e-9,'cases':[]}
env=os.environ.copy();env.update(PYTHONDONTWRITEBYTECODE='1',OMP_NUM_THREADS='1',OPENBLAS_NUM_THREADS='1',MKL_NUM_THREADS='1')
for name,relative in cases:
    started=time.perf_counter();original=jobs[name]['argv'];first=next(i for i,a in enumerate(original) if a.startswith('--'))
    assert original[first]=='--output'
    remote=original[first+1];rel=pathlib.Path(remote.split('/local-evidence/spread/',1)[1]);saved_path=base/rel
    saved=json.loads(saved_path.read_text());workspace=base/relative
    record=next(r for r in saved['paths'] if r['input'].endswith('/'+relative))
    assert record['status']=='completed'
    hashes={n:sha(workspace/n) for n in record['input_hashes']}
    assert hashes==record['input_hashes']
    result_path=out/(name+'.json')
    argv=[sys.executable,str(repo/'tools'/pathlib.Path(original[1]).name),str(workspace),*original[first:]]
    argv[argv.index('--output')+1]=str(result_path)
    assert saved['configuration']['device']=='cpu' and saved['configuration']['precision']=='fp64' and saved['configuration']['threads']==1
    log=out/(name+'.log');status=out/(name+'.status.json')
    runner=[sys.executable,str(repo/'tools/triad_bounded_run.py'),'--cwd',str(repo),'--log',str(log),'--status',str(status),'--timeout','60','--max-seconds','60','--term-grace','1','--',*argv]
    print('START',name,flush=True)
    run=subprocess.run(runner,stdin=subprocess.DEVNULL,capture_output=True,text=True,env=env)
    row={'case':name,'input':relative,'original_report':str(rel),'original_report_sha256':sha(saved_path),'input_sha256':hashes,'argv':argv,'runner_argv':runner,'runner_returncode':run.returncode,'runner_stderr':run.stderr,'wall_seconds':time.perf_counter()-started}
    if status.is_file():row['runner_status']=json.loads(status.read_text())
    if run.returncode!=0 or not result_path.is_file():
        row['state']='replay_failed';receipt['cases'].append(row);print('FAIL',name,run.returncode,flush=True);continue
    replay=json.loads(result_path.read_text());rr=replay['paths'][0]
    assert replay['configuration']==saved['configuration'] and replay['family']==saved['family']
    assert replay['source_sha256']==saved['source_sha256'] and replay['dependency_sha256']==saved['dependency_sha256']
    assert rr['status']=='completed' and replay['failed_physical_path_records']==0
    assert replay['sampled_physical_paths']==1 and len(replay['paths'])==1
    assert rr['input_hashes']==record['input_hashes']
    row.update(source_sha256=replay['source_sha256'],dependency_sha256=replay['dependency_sha256'],replay_report_sha256=sha(result_path),replay_scores_sha256=replay['scores_sha256'])
    with np.load(base/rel.parent/saved['scores_file'],allow_pickle=False) as old,np.load(result_path.parent/replay['scores_file'],allow_pickle=False) as new:
        assert sha(base/rel.parent/saved['scores_file'])==saved['scores_sha256'] and sha(result_path.parent/replay['scores_file'])==replay['scores_sha256']
        methods={}
        for method,details in record['conditions'][0]['methods'].items():
            after=rr['conditions'][0]['methods'][method];a=old[details['score_array']];b=new[after['score_array']]
            assert a.shape==b.shape==(1,2) and np.isfinite(a).all() and np.isfinite(b).all()
            maximum=float(np.max(np.abs(a-b)));aw=int(a[0].argmax());bw=int(b[0].argmax());gate=details['threshold']
            close=bool(np.allclose(a,b,rtol=receipt['rtol'],atol=receipt['atol']))
            assert close and aw==bw and details['counts']==after['counts'] and gate==after['threshold']
            assert bool(a[0,aw]>=gate)==bool(b[0,bw]>=gate)
            methods[method]={'saved_scores':a.tolist(),'replayed_scores':b.tolist(),'maximum_absolute_difference':maximum,'bit_identical':bool(np.array_equal(a,b)),'allclose':close,'saved_winner':aw,'replayed_winner':bw,'strict_threshold':gate,'saved_strict_admitted':bool(a[0,aw]>=gate),'replayed_strict_admitted':bool(b[0,bw]>=gate),'saved_counts':details['counts'],'replayed_counts':after['counts'],'relaxed_threshold':17.4227460230377,'saved_relaxed_admitted':bool(a[0,aw]>=17.4227460230377),'replayed_relaxed_admitted':bool(b[0,bw]>=17.4227460230377)}
        row['methods']=methods
    row['state']='matched';receipt['cases'].append(row)
    print('PASS',name,'max_diff',max(v['maximum_absolute_difference'] for v in methods.values()),'seconds',row['wall_seconds'],flush=True)
receipt['state']='passed' if len(receipt['cases'])==3 and all(r['state']=='matched' for r in receipt['cases']) else 'failed'
receipt['limitations']=['Original stable export campaign timed out after 288 successes; final stable case00000 is one of those original successes, not one of the 12 resumed indices. The prior independent identity audit establishes the resumption history.','Replaying three retained actual observations tests artifact/code numerical reproducibility only; not an independent signal/channel draw, full campaign rerun, or empirical false-admission qualification.']
receipt_path=out/'receipt.json';receipt_path.write_text(json.dumps(receipt,indent=2,allow_nan=False)+'\n')
print('RECEIPT',receipt['state'],str(receipt_path),sha(receipt_path),flush=True)
sys.exit(0 if receipt['state']=='passed' else 1)
