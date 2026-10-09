from pathlib import Path
import subprocess,csv,json,os,time,hashlib,statistics,io
r=Path('/home/user/___quick/p/_cur/dataPump/pumpModem');a=r/'.agent-work/artifacts/receiver-opt-20261008';out=a/'dsss-performance';out.mkdir(exist_ok=True)
cpus=sorted(os.sched_getaffinity(0));pin=cpus[min(2,len(cpus)-1)]
meta={'pin_cpu':pin,'available_cpus':cpus,'start_utc':time.strftime('%Y-%m-%dT%H:%M:%SZ',time.gmtime()),'source_lib_sha256':hashlib.sha256((r/'build/agents/receiver-opt-20261008/native/libdatapump.a').read_bytes()).hexdigest(),'scope':'serial isolated paired processes; original immutable PCM identical within pair; setup/push/poll timed by drivers; generation outside timed receiver'}
(out/'environment.json').write_text(json.dumps(meta,indent=2))
def run(cmd,timeout=60):
 start=time.monotonic()
 try:
  v=subprocess.run([str(x) for x in cmd],cwd=r,text=True,capture_output=True,timeout=timeout,preexec_fn=lambda:os.sched_setaffinity(0,{pin}));return {'exit':v.returncode,'stdout':v.stdout,'stderr':v.stderr,'outer_wall_seconds':time.monotonic()-start}
 except subprocess.TimeoutExpired as e:return {'exit':'timeout','stdout':str(e.stdout or ''),'stderr':str(e.stderr or ''),'outer_wall_seconds':time.monotonic()-start}
results={}
common=['--case','fast','--bits','001','--workspace-bytes','67108864','--workers','1','--uncertainty','.01','--phase-diffusion','.005','--repeats','1','--warmups','0','--target-cn0','70']
short={
 'ordinary': ['--bandwidth','3600','--carrier','1500','--dsss-factor','1','--seconds','8','--input-cn0','55'],
 'dsss10': ['--bandwidth','360','--carrier','1500','--dsss-factor','10','--seconds','12','--input-cn0','35'],
 'dsss100': ['--bandwidth','36','--carrier','1500','--dsss-factor','100','--seconds','24','--input-cn0','25'],
 'dsss1000': ['--bandwidth','3.6','--carrier','1500','--dsss-factor','1000','--seconds','160','--input-cn0','15'],
 'wider': ['--bandwidth','1200','--carrier','9000','--dsss-factor','10','--seconds','10','--input-cn0','45','--shift','1000000','--rf-oscillator','gpsdo-ocxo'],
 'weaker': ['--bandwidth','360','--carrier','1500','--dsss-factor','10','--seconds','12','--input-cn0','28'],
 'dsss10-fixed48000': ['--bandwidth','360','--carrier','1500','--sample-rate','48000','--dsss-factor','10','--seconds','12','--input-cn0','35']}
for name,args in short.items():
 pcm=out/(name+'.f32');entry={'args':common+args,'runs':[]};results[name]=entry
 gen=run([a/'benchmark-dsss-before',*common,*args,'--generate-only','--output-pcm',pcm],30);entry['generation']=gen
 if gen['exit']==0:
  entry['pcm_sha256']=hashlib.sha256(pcm.read_bytes()).hexdigest()
  for rep in range(3):
   for variant in (['before','after'] if rep%2==0 else ['after','before']):
    row=run([a/('benchmark-dsss-'+variant),*common,*args,'--input-pcm',pcm,'--variant',variant],45);row.update(variant=variant,repeat=rep)
    if row['exit']==0:row['rows']=list(csv.DictReader(io.StringIO(row['stdout'].replace('\\n','\n'))))
    entry['runs'].append(row);print(name,rep,variant,row['exit'],round(row['outer_wall_seconds'],3),flush=True)
   (out/'short-results.json').write_text(json.dumps(results,indent=2))
   if any(v['exit']!=0 for v in entry['runs']):break
 (out/'short-results.json').write_text(json.dumps(results,indent=2))
print('SHORT DONE',flush=True)
olddata=json.loads((a/'interval-measurements/final-performance-data.json').read_text());long={}
for name in ['primary-low','primary-mid','primary-high','strong','rf30','sub9','primary-low-fixed6000','primary-mid-fixed6000','affine-complete-cn0-2']:
 item=olddata[name];args=item['args'];pcm=a/'interval-measurements'/(name+'-final.f32')
 entry={'args':args,'pcm':str(pcm),'pcm_sha256':hashlib.sha256(pcm.read_bytes()).hexdigest(),'runs':[]};long[name]=entry
 for rep in range(3):
  for variant in (['before','after'] if rep%2==0 else ['after','before']):
   dest=out/(name+'-'+str(rep)+'-'+variant+'.csv')
   row=run([a/('benchmark-long-'+variant),*args,'--variant','automatic','--implementation-id',variant+'-dsss-followup','--input-pcm',pcm,'--csv',dest],60)
   row.update(variant=variant,repeat=rep)
   if row['exit']==0:row['rows']=list(csv.DictReader(dest.open()))
   entry['runs'].append(row);print(name,rep,variant,row['exit'],round(row['outer_wall_seconds'],3),flush=True)
  (out/'long-results.json').write_text(json.dumps(long,indent=2))
  if any(v['exit']!=0 for v in entry['runs']):break
 for variant in ['before','after']:
  if name not in ['primary-high','rf30','sub9','affine-complete-cn0-2']:break
  dest=out/(name+'-component-'+variant+'.csv')
  row=run([a/('benchmark-long-'+variant),*args,'--variant','automatic','--implementation-id',variant+'-dsss-followup','--input-pcm',pcm,'--instrumented','--csv',dest],60)
  row.update(variant=variant,instrumented=True)
  if row['exit']==0:row['rows']=list(csv.DictReader(dest.open()))
  entry.setdefault('component_runs',[]).append(row)
 (out/'long-results.json').write_text(json.dumps(long,indent=2))
print('ALL DONE',flush=True)
