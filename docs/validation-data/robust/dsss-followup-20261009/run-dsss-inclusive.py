from pathlib import Path
import json,subprocess,os,csv,io,time
r=Path.cwd();a=r/'.agent-work/artifacts/receiver-opt-20261008';d=a/'dsss-performance';old=json.loads((d/'short-results.json').read_text());pin=json.loads((d/'environment.json').read_text())['pin_cpu'];out={}
for name,item in old.items():
 out[name]=[]
 for variant in ['before','after']:
  p=subprocess.run([str(a/('benchmark-dsss-inclusive-'+variant)),*item['args'],'--input-pcm',str(d/(name+'.f32')),'--variant',variant],text=True,capture_output=True,timeout=45,preexec_fn=lambda:os.sched_setaffinity(0,{pin}))
  row={'exit':p.returncode,'stderr':p.stderr,'stdout':p.stdout,'variant':variant}
  if p.returncode==0:row['rows']=list(csv.DictReader(io.StringIO(p.stdout.replace('\\n','\n'))))
  out[name].append(row);print(name,variant,p.returncode,flush=True)
  (d/'inclusive-short-results.json').write_text(json.dumps(out,indent=2))
print('ALL CLEANUP CHECKS JOINED',flush=True)
