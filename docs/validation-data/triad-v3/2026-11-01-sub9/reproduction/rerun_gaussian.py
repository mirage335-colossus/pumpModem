import concurrent.futures,json,pathlib,subprocess,sys,time
root=pathlib.Path('/workspace/triad');out=pathlib.Path('/workspace/sub9-screen-analysis')
old=json.loads((out/'analysis-runs.json').read_text());jobs=[x for x in old if x['method']=='gaussian']
def run(row):
 cmd=row['command'];i=cmd.index('--output')+1;cmd[i]=cmd[i].replace('-gaussian.json','-gaussian-fixed.json');t=time.monotonic()
 with (out/(row['case']+'-gaussian-fixed.log')).open('x') as f:r=subprocess.run(cmd,stdout=f,stderr=subprocess.STDOUT)
 result={'case':row['case'],'returncode':r.returncode,'seconds':time.monotonic()-t,'command':cmd};print(json.dumps({k:v for k,v in result.items() if k!='command'}),flush=True);return result
with concurrent.futures.ThreadPoolExecutor(max_workers=4) as pool:result=list(pool.map(run,jobs))
(out/'gaussian-reruns.json').write_text(json.dumps(result,indent=2));raise SystemExit(any(r['returncode'] for r in result))
