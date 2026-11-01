import argparse,concurrent.futures,json,pathlib,subprocess,sys,time
p=argparse.ArgumentParser();p.add_argument('--name',required=True);p.add_argument('--paths',type=int,required=True);p.add_argument('--rbpf-paths',type=int,default=0);p.add_argument('--gaussian-paths',type=int,default=0);p.add_argument('--cn0',required=True);p.add_argument('--coherence',default='inf');a=p.parse_args()
root=pathlib.Path('/workspace/triad');base=pathlib.Path('/workspace/sub9-holdout')/a.name;out=pathlib.Path('/workspace/sub9-holdout-analysis')/a.name;out.mkdir(parents=True,exist_ok=False)
jobs=[]
for method,n,shard in [('static',a.paths,50),('rbpf',a.rbpf_paths,16),('gaussian',a.gaussian_paths,16)]:
 for start in range(0,n,shard):jobs.append((min(n,start+shard),start,method))
jobs.sort()
def run(job):
 end,start,method=job;inputs=[str(base/f'path-{i:05d}') for i in range(start,end)];deadline=time.monotonic()+2200
 while not all((pathlib.Path(q)/'manifest.json').exists() for q in inputs):
  if (base/'campaign.json').exists():raise RuntimeError('missing completed path in '+a.name)
  if time.monotonic()>deadline:raise TimeoutError(a.name)
  time.sleep(2)
 stem=f'{method}-{start:05d}-{end:05d}';common=['--cn0',a.cn0,'--selection-methods','11','--duration-alternatives','4','--threads','1','--precision','fp64','--device','cpu','--output',str(out/(stem+'.json'))]
 if method=='static':cmd=[sys.executable,str(root/'tools/triad-static-frequency-study.py'),*inputs,*common,'--frequency-bound','.000001','--taps','2']
 elif method=='rbpf':cmd=[sys.executable,str(root/'tools/triad-rbpf-study.py'),*inputs,*common,'--frequency-bound','.000001','--phase-diffusion','.00012341341494884352','--coherence',a.coherence,'--particles','256','--resampling','none']
 else:cmd=[sys.executable,str(root/'tools/triad-noise-study.py'),*inputs,*common,'--mode','actual','--coherences','10000','100000','1000000','inf','--methods','gaussian-ou','residual-ucb','angular-gaussian','--calibration-scope','stationary','--calibration-alternatives','2']
 t=time.monotonic()
 with (out/(stem+'.log')).open('x') as log:r=subprocess.run(cmd,stdout=log,stderr=subprocess.STDOUT)
 row={'method':method,'start':start,'end':end,'returncode':r.returncode,'seconds':time.monotonic()-t,'command':cmd};print(json.dumps({k:v for k,v in row.items() if k!='command'}),flush=True);return row
with concurrent.futures.ThreadPoolExecutor(max_workers=4) as pool:rows=list(pool.map(run,jobs))
(out/'runs.json').write_text(json.dumps(rows,indent=2));raise SystemExit(any(r['returncode'] for r in rows))
