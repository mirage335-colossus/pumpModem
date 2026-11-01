import concurrent.futures,json,pathlib,subprocess,sys,time
root=pathlib.Path('/workspace/triad');base=pathlib.Path('/workspace/sub9-screen');out=pathlib.Path('/workspace/sub9-screen-analysis');out.mkdir(exist_ok=False)
plan=json.loads((base/'plan.json').read_text())['cases']
def run(item):
 name,exportcmd=item
 deadline=time.monotonic()+900
 while not (base/name/'campaign.json').exists():
  if time.monotonic()>deadline:raise TimeoutError('export not complete: '+name)
  time.sleep(2)
 paths=sorted((base/name).glob('path-*/manifest.json'));inputs=[str(p.parent) for p in paths]
 if len(inputs)!=2:raise RuntimeError('incomplete independent paths: '+name)
 cn0=exportcmd[exportcmd.index('--cn0')+1];tc=exportcmd[exportcmd.index('--ray-coherence-seconds')+1] if '--ray-coherence-seconds' in exportcmd else 'inf'
 common=['--cn0',cn0,'--selection-methods','11','--duration-alternatives','4','--threads','1','--precision','fp64','--device','cpu']
 cmds=[('static',[sys.executable,str(root/'tools/triad-static-frequency-study.py'),*inputs,*common,'--frequency-bound','.000001','--taps','2','--output',str(out/(name+'-static.json'))]),('rbpf',[sys.executable,str(root/'tools/triad-rbpf-study.py'),*inputs,*common,'--frequency-bound','.000001','--phase-diffusion','.00012341341494884352','--coherence',tc,'--particles','256','--resampling','none','--output',str(out/(name+'-rbpf.json'))]),('gaussian',[sys.executable,str(root/'tools/triad-noise-study.py'),*inputs,*common,'--mode','actual','--coherences','10000','100000','1000000','inf','--methods','gaussian-ou','residual-ucb','angular-gaussian','--calibration-scope','stationary','--calibration-alternatives','2','--output',str(out/(name+'-gaussian.json'))])]
 results=[]
 for method,cmd in cmds:
  start=time.monotonic()
  with (out/(name+'-'+method+'.log')).open('x') as log:r=subprocess.run(cmd,stdout=log,stderr=subprocess.STDOUT)
  row={'case':name,'method':method,'returncode':r.returncode,'seconds':time.monotonic()-start,'command':cmd};results.append(row);print(json.dumps({k:v for k,v in row.items() if k!='command'}),flush=True)
 return results
with concurrent.futures.ThreadPoolExecutor(max_workers=4) as pool:rows=[r for group in pool.map(run,plan) for r in group]
(out/'analysis-runs.json').write_text(json.dumps(rows,indent=2))
raise SystemExit(any(x['returncode'] for x in rows))
