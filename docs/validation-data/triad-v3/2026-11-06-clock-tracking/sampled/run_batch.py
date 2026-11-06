import subprocess,json,sys,resource,time
from pathlib import Path
D=Path('local-evidence/triad-tracking-200/sampled');p=json.loads((D/'short-batch-protocol.json').read_text());result=[];begin=resource.getrusage(resource.RUSAGE_CHILDREN);start=time.perf_counter()
for case in p['cases']:
 cmd=[sys.executable,str(D/'run.py'),'--tag',case['tag'],'--helper',case['helper'],'--seed',str(case['seed']),'--cap',str(case['cap']),'--tx-events','--rx-events','--methods',','.join(p['receiver_methods'])]
 if case.get('erase'):cmd+=['--erase']
 with (D/(case['tag']+'.log')).open('x') as log:r=subprocess.run(cmd,stdout=log,stderr=subprocess.STDOUT)
 row=dict(case=case,returncode=r.returncode,command=cmd);result.append(row)
 if r.returncode==0:
  measured=json.loads((D/case['tag']/'results.json').read_text());row['summary']=[dict(method=q['method'],decision=q['decision'],cpu_seconds=q['receiver_cpu_seconds_including_native_basis'],wall_seconds=q['receiver_wall_seconds'],erased_rows=q['receiver']['erased_rows']) for q in measured['results']]
  q=measured['results'][-1];row['conditional_diagnostic_success']=q['receiver']['complete_observation'] and q['decision']['candidate_bit']==0 and q['decision']['conditional_model_pass']
 c=resource.getrusage(resource.RUSAGE_CHILDREN);cpu=c.ru_utime+c.ru_stime-begin.ru_utime-begin.ru_stime
 (D/'short-batch-results.json').write_text(json.dumps(dict(cases=result,child_cpu_seconds=cpu,wall_seconds=time.perf_counter()-start,complete=len(result)==len(p['cases'])),indent=2)+'\n');print(json.dumps(row),flush=True)
 if r.returncode or not row.get('conditional_diagnostic_success') or cpu>300:sys.exit(4)
