#!/usr/bin/env python3
"""Postselected precision diagnostic; never replaces the frozen reception cohort."""
import argparse, hashlib, json, os, subprocess, sys, time
from pathlib import Path
p=argparse.ArgumentParser();p.add_argument('--root',type=Path,required=True);p.add_argument('--plan',type=Path,required=True);p.add_argument('--capture-root',type=Path,required=True);p.add_argument('--out',type=Path,required=True);a=p.parse_args()
if a.out.exists():raise SystemExit('fresh output required')
a.out.mkdir(parents=True);plan=json.loads(a.plan.read_text());old=json.loads((a.capture_root/'results.json').read_text())
sha=lambda f:hashlib.sha256(Path(f).read_bytes()).hexdigest()
closure={k:sha(a.root/k) for k in plan['source_file_sha256']}
assert closure==plan['source_file_sha256']
rows={t['id']:t for row in plan['rows'] for t in row['trials']}
selected=[]
for case in ('A850-best','B425-best','C850-moderate'):
 candidates=[r for r in old['records'] if r['case']==case and r['control']=='signal' and r['status']=='completed']
 selected.append(min(candidates,key=lambda r:abs(max(r['bit_log_likelihoods'])-r['gates']['strict'])))
env=os.environ.copy();env.update(OMP_NUM_THREADS='1',OPENBLAS_NUM_THREADS='1',MKL_NUM_THREADS='1')
records=[];start=time.monotonic()
for original in selected:
 ident=original['id'];base=rows[ident]['score_argv'];bindings={'ROOT':str(a.root),'PYTHON':sys.executable,'OUTPUT':str(a.capture_root)}
 command=[]
 for value in base:
  for k,v in bindings.items():value=value.replace('{'+k+'}',v)
  command.append(value)
 command.remove('--omit-group-scores')
 for label,device,precision in [('cpu64','cpu','fp64'),('cuda32','cuda','fp32')]:
  argv=list(command);output=a.out/(ident+'-'+label+'.json')
  for flag,value in [('--device',device),('--precision',precision),('--memory-mib','4096'),('--output',str(output))]:argv[argv.index(flag)+1]=value
  stem=ident+'-'+label;status=a.out/(stem+'.status.json');log=a.out/(stem+'.log')
  wrapped=[sys.executable,str(a.root/'tools/triad_bounded_run.py'),'--cwd',str(a.root),'--log',str(log),'--status',str(status),'--timeout','120','--max-seconds','120','--term-grace','3','--',*argv]
  mark=time.monotonic();proc=subprocess.run(wrapped,env=env,capture_output=True,text=True,timeout=130)
  r={'id':ident,'variant':label,'argv':argv,'returncode':proc.returncode,'wall_seconds':time.monotonic()-mark,'status_sha256':sha(status),'status':json.loads(status.read_text()),'reference_bit_ll':original['bit_log_likelihoods'],'reference_outcomes':original['outcomes']}
  if proc.returncode==0:
   score=json.loads(output.read_text());r.update(score_sha256=sha(output),bit_ll=score['bit_log_likelihoods'],outcomes=score['simulation_evaluation_only']['outcomes'],input_hashes=score['input_sha256'],source=score['receiver_source_sha256'],dependencies=score['dependency_sha256'])
   assert score['input_sha256']=={x['name']:x['sha256'] for x in original['input_files']}
   assert r['source']==closure['tools/triad_fft_acquire.py']
   r['max_abs_mixture_error_against_original_fp32']=max(abs(x-y) for x,y in zip(r['bit_ll'],r['reference_bit_ll']))
   r['decisions_unchanged']=r['outcomes']==r['reference_outcomes']
  records.append(r)
  print(json.dumps({'id':ident,'variant':label,'returncode':proc.returncode}),flush=True)
after={k:sha(a.root/k) for k in closure}
summary={'scope':'Postselected closest-to-strict-threshold signal in each of three completed cohorts; arithmetic diagnosis only, not new independent receptions or altered cohort decisions. CPU/GPU mixed precision is explicit in each report.','driver_sha256':sha(__file__),'plan_sha256':sha(a.plan),'source_before':closure,'source_after':after,'source_stable':closure==after,'selected_ids':[r['id'] for r in selected],'records':records,'elapsed_seconds':time.monotonic()-start}
(a.out/'summary.json').write_text(json.dumps(summary,indent=2)+'\n')
assert summary['source_stable'] and all(r['returncode']==0 for r in records)
