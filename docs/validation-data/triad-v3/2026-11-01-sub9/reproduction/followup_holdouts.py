import json,pathlib,subprocess,sys,time
root=pathlib.Path('/workspace/triad');status=pathlib.Path('/workspace/sub9-holdout-static30-status.json')
while not status.exists():time.sleep(3)
if json.loads(status.read_text())['exit_code']!=0:raise RuntimeError('first fixed cohort did not complete')
plan=json.loads((root/'local-evidence/triad-sub9-campaign/holdout_plan.json').read_text())['cohorts'][1:]
analyses=[];rows=[]
for c in plan:
 name=c['name'];dest='/workspace/sub9-holdout/'+name
 cmd=[sys.executable,str(root/'tools/triad-lowrate-campaign.py'),'--binary',str(root/'build/dev-cli/triad_long'),'--output',dest,'--paths',str(c['paths']),'--split','holdout','--seconds',str(c['seconds']),'--rate','.036','--sample-rate','1','--cn0',str(c['cn0']),'--block-seconds','1024','--jobs','12','--timeout','180','--ray-coherence-seconds',str(c.get('ray_coherence_seconds',0)),'--control',c.get('control','signal')]
 t=time.monotonic(); result=subprocess.run(cmd);row={'name':name,'command':cmd,'returncode':result.returncode,'seconds':time.monotonic()-t};rows.append(row);print(json.dumps(row),flush=True)
 if result.returncode:break
 rbpf=c.get('paired_rbpf_paths',c['paths'] if c.get('primary')=='rbpf-ucb' or c.get('control') else 0)
 gauss=c['paths'] if c.get('paired_gaussian') else 0
 acmd=[sys.executable,str(root/'local-evidence/triad-sub9-campaign/score_holdout.py'),'--name',name,'--paths',str(c['paths']),'--rbpf-paths',str(rbpf),'--gaussian-paths',str(gauss),'--cn0',str(c['cn0']),'--coherence',str(c.get('ray_coherence_seconds') or 'inf')]
 log=open('/workspace/sub9-'+name+'-analysis.log','x');analyses.append((name,subprocess.Popen(acmd,stdout=log,stderr=subprocess.STDOUT),log))
for name,child,log in analyses:
 code=child.wait();log.close();rows.append({'analysis':name,'returncode':code});print(json.dumps(rows[-1]),flush=True)
pathlib.Path('/workspace/sub9-followup-results.json').write_text(json.dumps(rows,indent=2));raise SystemExit(any(r['returncode'] for r in rows))
