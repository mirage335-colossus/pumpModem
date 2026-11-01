import concurrent.futures,json,os,pathlib,subprocess,sys,time
root=pathlib.Path('/workspace/triad'); out=pathlib.Path('/workspace/sub9-screen');out.mkdir(exist_ok=False)
base=[sys.executable,str(root/'tools/triad-lowrate-campaign.py'),'--binary',str(root/'build/dev-cli/triad_long'),'--paths','2','--split','development','--seconds','3283200','--rate','.036','--sample-rate','1','--block-seconds','1024','--jobs','2','--timeout','900']
cases=[]
for c in [-36,-46,-48,-50,-56]:cases.append((f'static-cn{abs(c)}',['--cn0',str(c)]))
for rate,fs in [('.0036',1),('.36',1),('3.6',4)]:cases.append(('rate'+rate,['--cn0','-46','--rate',rate,'--sample-rate',str(fs)]))
for tc in [10000,100000,1000000]:cases.append((f'ou{tc}',['--cn0','-46','--ray-coherence-seconds',str(tc)]))
cases.extend([('diurnal',['--cn0','-46','--diurnal-phase-radians','.5']),('outage',['--cn0','-46','--outage-seconds','864000','1296000','--outage-ramp-seconds','3600']),('each1',['--cn0','-46','--selective-perturbation','each-1']),('acquisition',['--cn0','-46','--start','-.0905','.0905','.0905','--epoch-offsets=-1,0,1'])])
commands=[(name,base+options+['--output',str(out/name)]) for name,options in cases]
(out/'plan.json').write_text(json.dumps({'scope':'development screen; two independently seeded physical paths per case, no reliability qualification','cases':commands},indent=2))
def run(item):
 name,cmd=item; start=time.monotonic()
 with (out/(name+'.log')).open('x') as f:r=subprocess.run(cmd,stdout=f,stderr=subprocess.STDOUT)
 row={'name':name,'returncode':r.returncode,'elapsed_seconds':time.monotonic()-start};print(json.dumps(row),flush=True);return row
with concurrent.futures.ThreadPoolExecutor(max_workers=6) as pool:results=list(pool.map(run,commands))
(out/'results.json').write_text(json.dumps(results,indent=2))
raise SystemExit(1 if any(x['returncode'] for x in results) else 0)
